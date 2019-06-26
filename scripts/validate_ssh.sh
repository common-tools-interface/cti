#!/bin/bash
######################################################################
# This script is designed to determine whether SSH is setup properly #
# and in the event of invalid setup remedy it in a reasonable way    #
######################################################################
if ssh -o PreferredAuthentications=publickey -o StrictHostKeyChecking=no $HOSTNAME /bin/true exit ; then
    echo "SSH is properly setup"
else
    echo "SSH is not properly setup..."
    echo "Attempting to diagnose issue..."
    if ! test -d ~/.ssh ; then
        echo "No .ssh directory. Creating one..."
        if mkdir ~/.ssh ; then
           echo "Successfully created ~/.ssh and required authroized_keys file"
        else
           echo "Failed to create directory."
           exit 1
        fi
    fi
    if ! test -f ~/.ssh/authorized_keys ; then
        echo "No authorized keys file. Creating..."
        if touch ~/.ssh/authorized_keys ; then
            echo "Created authorized keys file."
        else
            echo "Failed to create file. Aborting..."
            exit 1
        fi
    fi
    if ! test -f ~/.ssh/id_rsa.pub ; then
        echo "No public ssh key exists. Creating one..."
        YOUR_EMAIL="${USER}@cray.com"
        if ssh-keygen -t rsa -N "" -f $HOME/.ssh/id_rsa -C "$YOUR_EMAIL" ; then
            echo "Public ssh key generated as ~/.ssh/id_rsa.pub with no password"
        else
            echo "Failed to generate public ssh key. Aborting..."
            exit 1
        fi
    fi
    YOUR_KEY="$(cat ~/.ssh/id_rsa.pub)"
    if ! grep -Fxq "$YOUR_KEY" ~/.ssh/authorized_keys ; then
        echo "id_rsa.pub key not present in authorized keys. Attempting to append it..."
        if cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys ; then
            echo "Key successfully added. All repair/setup steps completed..."
        else
            echo "Failed to append key to file. Check if file exists and permissions are correct. Aborting..."
            exit 1
        fi
    fi
    echo "Attempting to verify SSH again..."
    if ssh -o PreferredAuthentications=publickey -o StrictHostKeyChecking=no $HOSTNAME /bin/true exit ; then
        echo "SSH now properly configured."
    else
        echo "SSH failed. Possible the current id_rsa.pub key requires a password. New key required for vaild SSH use."
        exit 1
    fi
fi
